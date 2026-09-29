/* The import order is the cascade: tokens, then what a component IS, then
 * where this editor puts it. */
import '@ultraviolet/ui/tokens.css';
import '@ultraviolet/ui/components.css';
import './app.css';
import { render } from 'solid-js/web';
import App from './App.jsx';

render(() => <App />, document.getElementById('root'));
